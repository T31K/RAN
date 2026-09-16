

CREATE PROCEDURE [dbo].[sp_delete_guild_member]
    @ChaNum int,
    @nReturn int	OUTPUT
AS    
    DECLARE @nGuNum int,
         @error_var int, 
         @rowcount_var int

	SET NOCOUNT ON
	
	SET @nReturn = 0
	SET	@nGuNum = 0
	SET @error_var = 0
	SET @rowcount_var = 0
	
    UPDATE ChaInfo 
    SET GuNum=0, GuPosition=0, ChaGuSecede=getdate()
    WHERE ChaNum=@ChaNum
    
    SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT    
    IF @error_var <> 0 OR @rowcount_var = 0
    BEGIN
        SET @nReturn = 0
        SET NOCOUNT OFF
        RETURN @nReturn
    END
    ELSE
    BEGIN    
        SET @nReturn = 1        
        SET NOCOUNT OFF
        RETURN @nReturn
    END


