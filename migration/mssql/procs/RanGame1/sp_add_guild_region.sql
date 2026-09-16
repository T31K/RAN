

CREATE Procedure [dbo].[sp_add_guild_region]
	@RegionID int,
    @GuNum    int,
    @RegionTax float,
	@nReturn int	OUTPUT	
AS
	DECLARE @error_var int, 
		    @rowcount_var int
		    
	SET NOCOUNT ON
		 
	SET @nReturn = 0	
	SET @error_var = 0
	SET @rowcount_var = 0
	
	IF EXISTS(SELECT * FROM GuildRegion WHERE RegionID=@RegionID)
	BEGIN -- 瘤开捞 粮犁窍搁
		UPDATE GuildRegion 
		SET GuNum=@GuNum, RegionTax=@RegionTax 
		WHERE RegionID=@RegionID
		
		SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
		IF @error_var <> 0 OR @rowcount_var = 0 -- 俊矾惯积
		BEGIN
			SET @nReturn = 0
			SET NOCOUNT OFF
			RETURN @nReturn
		END
	END
	ELSE
	BEGIN
		INSERT INTO GuildRegion (RegionID, GuNum, RegionTax) 
		VALUES (@RegionID, @GuNum, @RegionTax)
		
		SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
		IF @error_var <> 0 OR @rowcount_var = 0 -- 俊矾惯积
		BEGIN
			SET @nReturn = 0
			SET NOCOUNT OFF
			RETURN @nReturn
		END
	END
	SET @nReturn = 1
	SET NOCOUNT OFF
	RETURN @nReturn


